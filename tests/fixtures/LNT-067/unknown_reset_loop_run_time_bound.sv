// sverity-args: --top top
// The reset loop stops at a run-time count: whether it covers the words loaded elsewhere is undecided.
// expect: LNT-067 WARNING 9
module top(input logic clk, rst_ni, input int n, input logic [1:0] wa, input logic [7:0] wd, output logic [7:0] y);
  logic [7:0] words [4];
  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int i = 0; i < n; i++) words[i] <= '0;
    end else words[wa] <= wd;
  end
  assign y = words[wa];
endmodule
