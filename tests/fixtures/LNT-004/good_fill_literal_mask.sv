// sverity-args: --top top
// A fill literal takes the width of its context: the mask has the width of the data, so the operands agree.
// expect: LNT-004 INFO 6
// expect-none: LNT-004 WARNING
module top(input logic we, de, input logic [7:0] d, q, output logic [7:0] y);
  assign y = (de ? d : q) & (we ? '0 : '1);
endmodule
