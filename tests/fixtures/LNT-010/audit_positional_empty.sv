// sverity-args: --top top
// expect: LNT-010 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// Positional instance with empty slots: input b and outputs y, z are unconnected.
module sub(input logic a, b, output logic y, z);
  assign y = a & b; assign z = a | b;
endmodule
module top(input logic a, output logic q);
  sub u3(a, , , );
  assign q = a;
endmodule
