// sverity-args: --top top
// Generate iterations 2k and 2k+1 both write word k (index i / 2), so each of words 0 and 1 has two write processes.
// expect: LNT-049 WARNING 8
// expect-count: LNT-049 WARNING 1
// revert: rf[i / 2] <= d => rf[i] <= d
// revert-expect-none: LNT-049 WARNING
module top(input logic clk, we, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] rf [4];
  for (genvar i = 0; i < 4; i++) begin : gen_word
    always_ff @(posedge clk) if (we) rf[i / 2] <= d;
  end
  assign q = rf[0];
endmodule
