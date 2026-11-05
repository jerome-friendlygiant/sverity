// sverity-args: --top top
// Near-miss of bad_nonexistent_port: the named connection uses the port's real name .a(a).
// expect-none: LNT-003
module sub(input logic a, output logic y);
  assign y = a;
endmodule
module top(input logic a, output logic y);
  sub u(.a(a), .y(y));
endmodule
