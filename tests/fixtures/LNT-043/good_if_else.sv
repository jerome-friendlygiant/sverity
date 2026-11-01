// sverity-args: --top top
// if/else makes the choice explicit: no finding
// expect-none: LNT-043
module top(input logic clk, en, rst, d, output logic q);
  always_ff @(posedge clk) begin
    if (rst) q <= 1'b0;
    else if (en) q <= d;
  end
endmodule
