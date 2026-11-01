// sverity-args: --top top --option LNT-061.allow=$display
// expect-none: LNT-061
module top(input logic clk, input logic a, output logic q);
  always @(posedge clk) begin
    $display("q=%b", a);
    q <= a;
  end
endmodule
