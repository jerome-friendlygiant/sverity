// sverity-args: --top top
// expect: LNT-061 WARNING 9
// expect: LNT-061 WARNING 10
// expect: LNT-061 WARNING 11
// expect: LNT-061 WARNING 12
// expect: LNT-061 WARNING 13
module top(input logic clk, input logic a, output logic q);
  always @(posedge clk) begin
    $display("q=%b", q);
    q <= $random;
    $finish;
    $countbits(a, 1'b1);
    $fopen("log.txt");
  end
endmodule
