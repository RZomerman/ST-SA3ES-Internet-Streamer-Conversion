# Sony ST-SA3ES ESP32 RDS Injection Specification

Markdown conversion of `Sony_ST-SA3ES_ESP32_RDS_Injection_Final_Spec_Rev2_0.docx`.

## Files

1. [`01_RDS_Interface_and_Decoding_Evidence.md`](01_RDS_Interface_and_Decoding_Evidence.md)  
   Physical interface, capture method, RDS framing, decoded groups, and verified/open findings.

2. [`02_RDS_Architecture_Validation_and_Capture_Analysis.md`](02_RDS_Architecture_Validation_and_Capture_Analysis.md)  
   ESP32 injection architecture, firmware modules, validation plan, extended capture analysis, PS reconstruction, RadioText fragments, repetition, and scheduling.

3. [`03_RDS_Injection_Final_Spec_and_HelloWorld.md`](03_RDS_Injection_Final_Spec_and_HelloWorld.md)  
   Final bench specification, isolation requirements, `HELLO`/`HELLO WORLD` RDS groups, CRC rules, timer cadence, test procedure, and pass/fail criteria.

## Critical safety constraint

The original SAA6579T outputs and ESP32 outputs must never actively drive RDS-C or RDS-D simultaneously. Isolate the original outputs before active injection and verify the intended logic-level interface before powering the prototype.
