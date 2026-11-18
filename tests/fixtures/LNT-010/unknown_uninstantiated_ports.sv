// sverity-args: --top top
// expect-none: LNT-010
// expect: LNT-011 INFO 4
module unused_leaf(input logic a,b, output logic y); assign y = a ^ b; endmodule
module top(input logic d, output logic y); assign y = ~d; endmodule
