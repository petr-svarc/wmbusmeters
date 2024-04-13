/*
 Copyright (C) 2021 Olli Salonen (gpl-3.0-or-later)
 Copyright (C) 2022-2023 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cmath>
#include"meters_common_implementation.h"

int64_t convertString2Integer(string v) {
    int64_t int_val = 0;
    int strlen = v.length();

    debug("(minomess_sva - convertString2Integer) string length is: %d (%d)\n", v.length(), (int64_t) pow(16, strlen-1));
    debug("(minomess_sva - convertString2Integer) extracted value is '%c%c%c%c%c%c%c%c'\n", char(v[0]), char(v[1]), char(v[2]), char(v[3]), char(v[4]), char(v[5]), char(v[6]), char(v[7]));
    debug("(minomess_sva - convertString2Integer) extracted value converted to integers is:");
    for (int i=0; i < strlen; i++) {
        debug(" %i", char2int(char(v[i])));
    }
    debug("\n");

    // convert to integer value
    for (int i=0; i < strlen; i++) {
        int_val += (int64_t) char2int(char(v[i])) * (int64_t) pow(16, strlen - 1 - i);
        debug("(minomess_sva - convertString2Integer) processing %d. character, so far the value is: %d\n", i, int_val);
        debug("(minomess_sva - convertString2Integer) %d, %d, %d\n", i, strlen, (int64_t) pow(16, strlen - 1 - i));
    }
    return int_val;
}


namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);
    
    private:

        void processContent(Telegram *t);



    };

    static bool ok = registerDriver([](DriverInfo&di)
    {
        di.setName("minomess_sva");
        // di.setDefaultFields("name,id,total_m3,target_m3,status,timestamp");
        di.setDefaultFields("name,id,total_m3,target_m3,target_date,total_consumption_last_month_m3,last_month_date,total_consumption_prev_1_month_m3,status,timestamp");
        di.setMeterType(MeterType::WaterMeter);
        di.addLinkMode(LinkMode::C1);
        // di.addDetection(MANUFACTURER_ZRI, 0x07,  0x00);
        // di.addDetection(MANUFACTURER_ZRI, 0x16,  0x01);
        // di.addDetection(MANUFACTURER_ZRI, 0x06,  0x01);
        di.usesProcessContent();
        di.setConstructor([](MeterInfo& mi, DriverInfo& di){ return shared_ptr<Meter>(new Driver(mi, di)); });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di) : MeterCommonImplementation(mi, di)
    {
        addOptionalLibraryFields("meter_date,fabrication_no,operating_time_h,on_time_h,on_time_at_error_h,meter_datetime");
        addOptionalLibraryFields("total_m3,total_backward_m3,volume_flow_m3h");

        /* If the meter is recently commissioned, the target water consumption value is bogus.
           The bits store 0xffffffff. Should we deal with this? Now a very large value is printed in the json.

           The wmbus telegram contains only storage 8 for target_date and total. */
        addNumericFieldWithExtractor(
            "target",
            "The total water consumption recorded at the beginning of this month.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Volume,
            VifScaling::Auto,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Volume)
            .set(StorageNr(8))
            );

        addStringFieldWithExtractor(
            "target_date",
            "Date when target water consumption was recorded.",
            DEFAULT_PRINT_PROPERTIES,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Date)
            .set(StorageNr(8))
            );

        // add last month date
        addStringFieldWithExtractor(
            "last_month_date",
            "Date when previous month water consumption was recorded.",
            DEFAULT_PRINT_PROPERTIES,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Date)
            .set(StorageNr(8))
            );

        // The wire mbus telegram contains 4 totals and dates. For the moment we only print nr 1 which is the latest.
        addNumericFieldWithExtractor(
            "target",
            "The total water consumption recorded at the beginning of this month.",
            DEFAULT_PRINT_PROPERTIES,
            Quantity::Volume,
            VifScaling::Auto,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Volume)
            .set(StorageNr(1))
            );

        addStringFieldWithExtractor(
            "target_date",
            "Date when target water consumption was recorded.",
            DEFAULT_PRINT_PROPERTIES,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::Date)
            .set(StorageNr(1))
            );

        /*
          According to data sheet, there are two status/info bytes, byte A and byte B.
          Unfortunately we do not now is byte A is the first or second byte. Oh well.
          Now we guess that A is the hi byte. I.e. 0x8000 is byte A bit 7.
          In the telegram the byte order is: lo byte first followed by the hi byte.
          So the defacto telegram bytes would be 0x0080 for byte A bit 7.

          Byte A:
          bit 7 removal active in the past
          bit 6 tamper active in the past
          bit 5 leak active in the past
          bit 4 temporary error (in connection with smart functions)
          bit 3 permanent error (meter value might be lost)
          bit 2 battery EOL (measured)
          bit 1 abnormal error
          bit 0 unused

          Byte B:
          bit 7 burst
          bit 6 removal
          bit 5 leak
          bit 4 backflow in the past
          bit 3 backflow
          bit 2 meter blocked in the past
          bit 1 meter undersized
          bit 0 meter oversized
        */

        addStringFieldWithExtractorAndLookup(
            "status",
            "Status and error flags.",
            DEFAULT_PRINT_PROPERTIES,
            FieldMatcher::build()
            .set(DifVifKey("02FD17"))
            ,
            {
                {
                    {
                        "ERROR_FLAGS",
                        Translate::Type::BitToString,
                        AlwaysTrigger, MaskBits(0xffff),
                        "OK",
                        {
                            { 0x8000, "WAS_REMOVED" },
                            { 0x4000, "WAS_TAMPERED" },
                            { 0x2000, "WAS_LEAKING" },
                            { 0x1000, "TEMPORARY_ERROR" },
                            { 0x0800, "PERMANENT_ERROR" },
                            { 0x0400, "BATTERY_EOL" },
                            { 0x0200, "ABNORMAL_ERROR" },
                            // 0x0100 not used
                            { 0x0080, "BURSTING" },
                            { 0x0040, "REMOVED" },
                            { 0x0020, "LEAKING" },
                            { 0x0010, "WAS_BACKFLOWING" },
                            { 0x0008, "BACKFLOWING" },
                            { 0x0004, "WAS_BLOCKED" },
                            { 0x0002, "UNDERSIZED" },
                            { 0x0001, "OVERSIZED" }
                        }
                    },
                },
            });

        // add consumption at the end of last month
        addNumericField(
            "total_consumption_last_month",
            Quantity::Volume,
            DEFAULT_PRINT_PROPERTIES,
            "The total water consumptions recorded at the end of previous month.",
            Unit::M3
            );

        // add consumption at the end of previous months
        for (int i=1; i<15; i++) {
            addNumericField(
                tostrprintf("total_consumption_prev_%d_month", i),
                Quantity::Volume,
                DEFAULT_PRINT_PROPERTIES,
                tostrprintf("The total water consumptions recorded at the end of previous month no. %d.", i),
                Unit::M3
                );
            // addStringField(
            //     tostrprintf("prev_%d_month_date", i),
            //     tostrprintf("Date when previous month no. %d water consumption was recorded.", i),
            //     DEFAULT_PRINT_PROPERTIES
            //     );
        }

    }

    void Driver::processContent(Telegram *t)
    {
        map<string,pair<int,DVEntry>> dv_entries = t->dv_entries;
        string key;
        int offset;
        string v;
        string mnthly_val = "000000";
        double scale;
        int64_t volume_;
        double scaled_volume_;
        string field_name;

        debug("(minomess_sva - process content) processing content ...\n");

        if(findKey(MeasurementType::Instantaneous, VIFRange::Volume, StorageNr(8), 0, &key, &dv_entries)) {
            field_name = "total_consumption_last_month";
            // get the DVEntry for the found key
            pair<int,DVEntry>&  p = (dv_entries)[key];

            debug("(minomess_sva - process content) found key '%s'...\n", key.c_str());
            extractDVReadableString(&t->dv_entries, key, &offset, &v);

            // convert to integer value
            scale = vifScale(p.second.dif_vif_key.vif());
            volume_ = convertString2Integer(v);
            scaled_volume_ = ((double) volume_) / scale;

            debug("(minomess_sva - process content) extracted value converted to integer is '%i'\n", volume_);
            debug("(minomess_sva - process content) scale is '%f'\n", scale);
            debug("(minomess_sva - process content) scaled value is '%f'\n", scaled_volume_);

            debug("(minomess_sva - process content) as usual: %s %s decoded %s value %g (scale %g)\n",
                toString(VIFRange::Volume),
                field_name.c_str(),
                unitToStringLowerCase(toDefaultUnit(p.second.vif)).c_str(),
                scaled_volume_,
                scale);

            t->addMoreExplanation(offset, " (%s: %f)", field_name.c_str(), scaled_volume_);
            setNumericValue(field_name, toDefaultUnit(p.second.vif), scaled_volume_);
        }

        if(findKeyWithNr(MeasurementType::Instantaneous, VIFRange::Volume, StorageNr(8), 0, 2, &key, &dv_entries)) {
            // get the DVEntry for the found key
            pair<int,DVEntry>&  p = (dv_entries)[key];

            debug("(minomess_sva - process content) found key '%s'...\n", key.c_str());
            extractDVReadableString(&dv_entries, key, &offset, &v);

            debug("(minomess_sva - convertString2Integer) extracted value is '%c%c%c%c %c%c%c%c%c%c'\n", char(v[0]), char(v[1]), char(v[2]), char(v[3]), char(v[4]), char(v[5]), char(v[6]), char(v[7]), char(v[8]), char(v[9]));

            debug("(minomess_sva - process content) extracted value is: 1234567890        20        3        40        50        60        70        80      88\n");
            debug("(minomess_sva - process content) extracted value is: ");
            for (int i=88; i>0; i=i-2) {
                debug("%c%c", char(v[i-2]), char(v[i-1]));
            }
            debug("\n");
            debug("(minomess_sva - process content) extracted value is: ");
            for (int i=88; i>0; i--) {
                debug("%c", char(v[i-1]));
            }
            debug("\n");

            for (int i=0; i<14; i++) {
                field_name = tostrprintf("total_consumption_prev_%d_month", (i+1));

                mnthly_val[0] = v[78-i*6];
                mnthly_val[1] = v[79-i*6];
                mnthly_val[2] = v[80-i*6];
                mnthly_val[3] = v[81-i*6];
                mnthly_val[4] = v[82-i*6];
                mnthly_val[5] = v[83-i*6];

                debug("(minomess_sva - process content) processing %d. month\n", (i+1));
                debug("(minomess_sva - process content) monthly value is '%c%c%c%c%c%c'\n", char(mnthly_val[0]), char(mnthly_val[1]), char(mnthly_val[2]), char(mnthly_val[3]), char(mnthly_val[4]), char(mnthly_val[5]));

                // if the first value is '8', then it is an initial value and we don't want it
                if (char(mnthly_val[0]) != '8') {
                    // convert to integer value
                    scale = vifScale(p.second.dif_vif_key.vif());
                    volume_ = convertString2Integer(mnthly_val);
                    scaled_volume_ = ((double) volume_) / scale;

                    debug("(minomess_sva - process content) extracted value converted to integer is '%i'\n", volume_);
                    debug("(minomess_sva - process content) scale is '%f'\n", scale);
                    debug("(minomess_sva - process content) scaled value is '%f'\n", scaled_volume_);

                    debug("(minomess_sva - process content) as usual: %s %s decoded %s value %g (scale %g)\n",
                        toString(VIFRange::Volume),
                        field_name.c_str(),
                        unitToStringLowerCase(toDefaultUnit(p.second.vif)).c_str(),
                        scaled_volume_,
                        scale);

                    t->addMoreExplanation(offset, " (%s: %f)", field_name.c_str(), scaled_volume_);
                    setNumericValue(field_name, toDefaultUnit(p.second.vif), scaled_volume_);
                }
            }
        }
    }
}

    // 00: 66 length (102 bytes)
    // 01: 44 dll-c (from meter SND_NR)
    // 02: 496a dll-mfct (ZRI)
    // 04: 10640355 dll-id (55036410)
    // 08: 14 dll-version
    // 09: 37 dll-type (Radio converter (meter side))
    // 0a: 72 tpl-ci-field (EN 13757-3 Application Layer (long tplh))
    // 0b: 51345015 tpl-id (15503451)
    // 0f: 496a tpl-mfct (ZRI)
    // 11: 00 tpl-version
    // 12: 07 tpl-type (Water meter)
    // 13: 76 tpl-acc-field
    // 14: 00 tpl-sts-field (OK)
    // 15: 5005 tpl-cfg 0550 (AES_CBC_IV nb=5 cntn=0 ra=0 hc=0 )
    // 17: 2f2f decrypt check bytes

    // 19: 0C dif (8 digit BCD Instantaneous value)
    // 1a: 13 vif (Volume l)
    // 1b: * 55140000 total consumption (1.455000 m3)
    // 1f: 02 dif (16 Bit Integer/Binary Instantaneous value)
    // 20: 6C vif (Date type G)
    // 21: * A92B meter date (2021-11-09)
    // 23: 82 dif (16 Bit Integer/Binary Instantaneous value)
    // 24: 04 dife (subunit=0 tariff=0 storagenr=8)
    // 25: 6C vif (Date type G)
    // 26: * A12B target consumption reading date (2021-11-01)
    // 28: 8C dif (8 digit BCD Instantaneous value)
    // 29: 04 dife (subunit=0 tariff=0 storagenr=8)
    // 2a: 13 vif (Volume l)
    // 2b: * 71000000 target consumption (0.071000 m3)
    //
    // 2f: 8D dif (variable length Instantaneous value)
    // 30: 04 dife (subunit=0 tariff=0 storagenr=8)
    // 31: 93 vif (Volume l)
    // 32: 13 vife (Reverse compact profile without register)
    // 33: 2C varlen=44
    //  This register has 24-bit integers for the consumption of the past months n-2 until n-15.
    //  If the meter is commissioned less than 15 months ago, you will see FFFFFF as the value.
    //          n-2    n-3    n-4    n-5    n-6    n-7    n-8    n-9    n-10   n-11   n-12   n-13   n-14   n-15
    // 34: FBFE 000000 FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF FFFFFF
    //
    // 60: 02 dif (16 Bit Integer/Binary Instantaneous value)
    // 61: FD vif (Second extension FD of VIF-codes)
    // 62: 17 vife (Error flags (binary))
    // 63: * 0000 info codes (OK)


// Test: Mino minomess_sva 15503451 NOKEY
// telegram=|6644496A1064035514377251345015496A0007EE0050052F2F#0C1359000000026CBE2B82046CA12B8C0413FFFFFFFF8D0493132CFBFEFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF02FD1700002F2F|
// {"media":"water","meter":"minomess_sva","name":"Mino","id":"15503451","meter_date":"2021-11-30","total_m3":0.059,"target_date":"2021-11-01","status":"OK","timestamp":"1111-11-11T11:11:11Z"}
// |Mino;15503451;0.059;null;OK;1111-11-11 11:11.11

// Test: Minowired minomess_sva 57575757 NOKEY
// telegram=|6874746808007257575757496A000712000000_0C7857575757046D2414DE280413000000000C943C000000004413FFFFFFFF426CFFFF840113FFFFFFFF82016CFFFFC40113FFFFFFFFC2016CFFFF840213FFFFFFFF82026CFFFF043B000000000422E62F000004260000000034220000000002FD1700001F5716|
// {"media":"water","meter":"minomess_sva","name":"Minowired","id":"57575757","fabrication_no":"57575757","operating_time_h":0,"on_time_h":12262,"on_time_at_error_h":0,"meter_datetime":"2022-08-30 20:36","total_m3":0,"total_backward_m3":0,"volume_flow_m3h":0,"target_m3":4294967.295,"target_date":"2127-15-31","status":"OK","timestamp":"1111-11-11T11:11:11Z"}
// |Minowired;57575757;0;4294967.295;OK;1111-11-11 11:11.11

// Test: Zenner_cold minomess_sva 21314151 NOKEY
// telegram=|6644496A4425155518377251413121496A0116360050052F2F_0C1355000000026CEC2182046CE1218C0413000000808D0493132C33FE00008000008000008000008000008000008000008000008000008000008000008000008000008000008002FD1700002F2F|
// {"media":"cold water","meter":"minomess_sva","name":"Zenner_cold","id":"21314151","meter_date":"2023-01-12","total_m3":0.055,"target_m3":80000,"target_date":"2023-01-01","status":"OK","timestamp":"1111-11-11T11:11:11Z"}
// |Zenner_cold;21314151;0.055;80000;OK;1111-11-11 11:11.11

// Test: Zenner_warm minomess_sva 51413121 NOKEY
// telegram=|6644496A8753155518377221314151496A0106300050052F2F_0C1357000000026CEC2182046CE1218C0413000000808D0493132C33FE00008000008000008000008000008000008000008000008000008000008000008000008000008000008002FD1700002F2F|
// {"media":"warm water","meter":"minomess_sva","name":"Zenner_warm","id":"51413121","meter_date":"2023-01-12","total_m3":0.057,"target_m3":80000,"target_date":"2023-01-01","status":"OK","timestamp":"1111-11-11T11:11:11Z"}
// |Zenner_warm;51413121;0.057;80000;OK;1111-11-11 11:11.11
