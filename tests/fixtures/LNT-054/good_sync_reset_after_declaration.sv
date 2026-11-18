// expect-none: LNT-054
module top(input logic clk, rst, d, output logic q);
  logic state;
  always @(posedge clk) begin
    automatic logic temporary;
    if (rst) state <= 0; else state <= d;
  end
  always_comb if (state) q = d; else q = 0;
endmodule
