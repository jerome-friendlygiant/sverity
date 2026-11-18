// sverity-args: --top top
// Parts of one register reset by separate constant writes, and an array reset by a loop over every element, are fully reset.
// expect-none: LNT-054
module top(input logic clk, rst, d, output logic y);
  logic [1:0] pair;
  logic [1:0] words [2];
  always @(posedge clk) if (rst) begin pair[0] <= 0; pair[1] <= 0; end else pair <= {d, d};
  always @(posedge clk) if (rst) begin for (int i = 0; i < 2; i++) words[i] <= '0; end else words[0] <= {d, d};
  always_comb begin
    y = 0;
    if (pair[1]) y = 1;
    if (words[1][0]) y = 1;
  end
endmodule
