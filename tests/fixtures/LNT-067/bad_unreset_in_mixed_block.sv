// sverity-args: --top top
// expect: LNT-067 WARNING 9
module top(input logic clk, rst_n, a, output logic q, r, s);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q <= 1'b0;
      r <= 1'b0;
    end else begin
      s <= a;
      r <= a;
    end
  end
endmodule
