// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q;
  task automatic outer(output logic o); {o, q} = 2'b11; endtask
  always @(posedge clk) outer(q);
  assign y = q;
endmodule
