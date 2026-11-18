// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q, r;
  task automatic inner(output logic [1:0] v); v = 2'b11; endtask
  task automatic outer(output logic o); inner({o, q}); endtask
  always @(posedge clk) outer(r);
  assign y = q;
endmodule
