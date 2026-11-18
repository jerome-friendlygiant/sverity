// sverity-args: --top top
// expect: LNT-079 WARNING 8
// expect: LNT-079 WARNING 9
// expect: LNT-079 WARNING 10
// expect: LNT-079 WARNING 11
module top(input logic clk, input logic d, output logic q, output logic r);
  always @(posedge clk) begin
    force q = d;
    release q;
    assign r = d;
    deassign r;
  end
endmodule
