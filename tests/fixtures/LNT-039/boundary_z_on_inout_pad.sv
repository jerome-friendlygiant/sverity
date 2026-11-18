// sverity-args: --top top
// A pad that is driven only when the enable is high, and released otherwise, is a tri-state output: INFO.
// expect: LNT-039 INFO 6
// expect-none: LNT-039 WARNING
module top(input logic en, d, inout wire pad, output logic q);
  assign pad = en ? d : 1'bz;
  assign q = pad;
endmodule
