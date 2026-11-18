// sverity-args: --top top
// Assertions are exempt: the release in the action block is not reported; the force beside it is.
// expect: LNT-079 WARNING 7
// expect-count: LNT-079 WARNING 1
module top(input logic clk, input logic d, output logic q, output logic r);
  always @(posedge clk) begin
    force q = d;
    assert (d) else begin release r; end
  end
endmodule
