// sverity-args: --top top
// Near-miss of bad_unconnected_input: the input b is connected, so no port is left open.
// expect-none: LNT-010
module child(input logic a, b, output logic y);
  assign y = a & b;
endmodule
module top(input logic a, output logic y);
  child u(.a(a), .b(a), .y(y));
endmodule
