// sverity-args: --top top
// Near-miss of bad_nonexistent_port: an implicit .* connection binds every port by its own name, so nothing is illegal.
// expect-none: LNT-003
module sub(input logic a, output logic y);
  assign y = a;
endmodule
module top(input logic a, output logic y);
  sub u(.*);
endmodule
