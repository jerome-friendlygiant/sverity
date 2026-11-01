// sverity-args: --top top
// every register has a reset value
// expect-none: LNT-067
module top(input logic clk, rst_n, a, output logic q, r);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin q <= 1'b0; r <= 1'b0; end
    else begin q <= a; r <= a; end
endmodule
