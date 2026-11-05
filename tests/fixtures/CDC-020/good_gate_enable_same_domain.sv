// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_gate_enable_other_domain.sv: the enable register is clocked by clka, the gated clock's own domain, instead of clkb.
// expect-none: CDC-020
module top(input logic clka, clkb, rst_n, d, en_d, output logic q);
  logic en, gclk;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) en <= 1'b0; else en <= en_d;
  assign gclk = clka & en;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
