// sverity-args: --top top
// A procedural assign in a task that an always block calls is reported where it is written.
// expect: LNT-079 WARNING 6
module top(input logic clk, input logic d, output logic q);
  task automatic hold();
    assign q = d;
  endtask
  always @(posedge clk) hold();
endmodule
