// sverity-args: --top top
// expect: LNT-011 INFO 3
module unused_leaf(input logic a, output logic y); assign y = ~a; endmodule
module top(input logic d, output logic y); assign y = ~d; endmodule
