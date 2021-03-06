/*
 * Copyright (c) 2020 Particle Industries, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tracker_shipping.h"

#define SHIPPING_MODE_LED_CYCLE_PERIOD_MS       (250)
#define SHIPPING_MODE_LED_CYCLE_DURATION_MS     (5000)
#define SHIPPING_MODE_DEFER_DURATION_MS         (5000) // 5 seconds

TrackerShipping *TrackerShipping::_instance = nullptr;

int TrackerShipping::regShutdownCallback(shipping_mode_shutdown_cb_t cb)
{
    shutdown_cb = cb;

    return 0;
}

void TrackerShipping::pmicHandler()
{
    TrackerShipping::instance()._pmicFire = true;
}

void TrackerShipping::shutdown()
{
    PMIC pmic;

    // blink RGB to signal entering shipping mode
    RGB.control(true);
    RGB.brightness(255);
    for(int i=0;
        i < (SHIPPING_MODE_LED_CYCLE_DURATION_MS / SHIPPING_MODE_LED_CYCLE_PERIOD_MS);
        i++)
    {
        // cycle between primary colors
        RGB.color(((uint32_t) 0xFF) << ((i % 3) * 8));
        HAL_Delay_Milliseconds(SHIPPING_MODE_LED_CYCLE_PERIOD_MS);
    }

    auto shipping = &TrackerShipping::instance();
    if (shipping->_checkPower)
    {
        // Attach and own the PMIC interrupt in order to provide the quickest
        // way to figure out changes in PMIC input power right before going into
        // shipping mode.
        attachInterrupt(PMIC_INT, &TrackerShipping::pmicHandler, FALLING);
    }

    WITH_LOCK(pmic)
    {
        pmic.disableWatchdog();
        if (shipping->_checkPower && shipping->_pmicFire)
        {
            // If the PMIC interrupted us then reset instead of going into shipping mode because
            // the power is likely to be applied between when the mode was commanded and the delayed
            // response of this particular handler.
            System.reset();
        }
        pmic.disableBATFET();
    }

    RGB.brightness(0);

    // sleep forever waiting for power to be removed
    // leave network on for quicker drain of residual power
    // once main power is removed
    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::HIBERNATE)
      .gpio(PMIC_INT, FALLING);
    System.sleep(config);

    // shouldn't hit these lines as never coming back from sleep but out of an
    // abundance of paranoia force a reset so we don't get stuck in some weird
    // pseudo-shutdown state
    System.reset();
}

int TrackerShipping::enter(bool checkPower)
{
    if(shutdown_cb)
    {
        int rval = shutdown_cb();

        if(rval)
        {
            return rval;
        }
    }

    // This flag will allow the shipping mode code to check power state before shutting down
    _checkPower = checkPower;

    // Timer call will shutdown device so don't worry about dynamic memory
    auto deferredShutdown = new Timer(SHIPPING_MODE_DEFER_DURATION_MS, TrackerShipping::shutdown, true);
    deferredShutdown->start();

    return 0; // compiler warnings about no return...
}

int TrackerShipping::enter_cb(CloudServiceStatus status, JSONValue *root, const void *context)
{
    return enter();
}

void TrackerShipping::init()
{
    CloudService::instance().regCommandCallback("enter_shipping", &TrackerShipping::enter_cb, this);
}
