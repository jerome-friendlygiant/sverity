// sverity-args: --top top
// expect: LNT-061 INFO 7
// expect: LNT-061 INFO 9
// expect-none: LNT-061 WARNING
module top(input logic clk, input logic a, output logic q);
  always_ff @(posedge clk) q <= a;
  final $display("done");
  always_comb begin
    assert (a) else $error("a low");
  end
endmodule
