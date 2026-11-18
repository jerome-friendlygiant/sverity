// sverity-args: --top top
// The reset loop clears words 0 and 1; the other branch loads any word by a run-time address.
// expect: LNT-067 INFO 9
module top(input logic clk, rst_ni, input logic [1:0] wa, input logic [7:0] wd, output logic [7:0] y);
  logic [7:0] words [4];
  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int i = 0; i < 2; i++) words[i] <= '0;
    end else words[wa] <= wd;
  end
  assign y = words[wa];
endmodule
