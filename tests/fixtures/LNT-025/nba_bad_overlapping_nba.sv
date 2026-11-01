// sverity-args: --top top
// the reset update is written after the enable update: when both hold, the later nonblocking one wins
// expect: LNT-025 WARNING 7
module top(input logic clk, en, rst, d, output logic q);
  always_ff @(posedge clk) begin
    if (en) q <= d;
    if (rst) q <= 1'b0;
  end
endmodule
