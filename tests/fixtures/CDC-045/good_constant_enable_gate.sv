// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_gate_without_latch.sv: the enable register is replaced by a constant 1, so the gate enable never changes and no register or input drives it.
// expect-none: CDC-045
module top(input logic clka, rst_n, d, output logic q);
  logic gclk;
  assign gclk = clka & 1'b1;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
