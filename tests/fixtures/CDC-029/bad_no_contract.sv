// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-029 WARNING
module top(input logic clka, rst_n, din, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= din;
endmodule
