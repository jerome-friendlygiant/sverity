// sverity-args: --top top --allow-dup-initial-drivers
// A slang relaxation flag turns errors off; the run discloses that its upstream diagnostics are incomplete.
// expect-coverage: upstream-diagnostics INCOMPLETE
// expect-none: LNT-001
module top(input logic clk, d, output logic y);
  initial y = 1'b0;
  always_ff @(posedge clk) y <= d;
endmodule
