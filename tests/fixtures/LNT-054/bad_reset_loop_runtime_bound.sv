// sverity-args: --top top
// The reset loop stops at a run-time count, so it does not certainly reset every element.
// expect: LNT-054 WARNING 9
module top(input logic clk, rst, d, input int n, output logic y);
  logic [1:0] words [2];
  always @(posedge clk) if (rst) begin for (int i = 0; i < n; i++) words[i] <= '0; end else words[0] <= {d, d};
  always_comb begin
    y = 0;
    if (words[1][0]) y = 1;
  end
endmodule
// revert: i < n => i < 2
// revert-expect-none: LNT-054
