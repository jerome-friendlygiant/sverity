// sverity-args: --top top
// a counter updated only by its own blocking self-read (cnt = cnt + 1) has no later assignment: the intended idiom, no finding
// expect-none: LNT-044
module top(input logic clk, en, output logic q);
  logic [3:0] cnt;
  always @(posedge clk) begin
    if (en) cnt = cnt + 1;
    q = cnt[0];
  end
endmodule
