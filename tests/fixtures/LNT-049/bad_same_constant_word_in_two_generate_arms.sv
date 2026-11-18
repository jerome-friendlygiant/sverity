// sverity-args: --top top
// Both generate arms are live and write word 1.
// expect: LNT-049 WARNING 6
// expect-count: LNT-049 WARNING 1
module top(input logic clk, we, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] m [4];
  for (genvar k = 0; k < 2; k++) begin : gen_arm
    always_ff @(posedge clk) if (we) m[1] <= d + k[3:0];
  end
  assign q = m[1];
endmodule
