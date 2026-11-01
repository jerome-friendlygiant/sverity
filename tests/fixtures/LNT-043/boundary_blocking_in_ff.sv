// sverity-args: --top top
// blocking assignments in a clocked block are LNT-015 territory, not this nonblocking rule: no finding here
// expect-none: LNT-043
module top(input logic clk, en, rst, d, output logic q);
  logic t;
  always_ff @(posedge clk) begin
    t = d;
    if (en) t = 1'b1;
    if (rst) t = 1'b0;
    q <= t;
  end
endmodule
