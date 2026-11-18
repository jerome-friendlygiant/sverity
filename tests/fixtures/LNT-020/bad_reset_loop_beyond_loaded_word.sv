// sverity-args: --top top
// The reset loop clears four words, but the other branch loads only word 0: words 1 to 3 are reset-only.
// expect: LNT-020 WARNING 8
module top(input logic clk, rst_ni, input logic [7:0] wd, output logic [7:0] y);
  logic [7:0] words [4];
  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int i = 0; i < 4; i++) words[i] <= '0;
    end else words[0] <= wd;
  end
  assign y = words[0];
endmodule
// revert: words[0] <= wd => for (int i = 0; i < 4; i++) words[i] <= wd
// revert-expect-none: LNT-020
