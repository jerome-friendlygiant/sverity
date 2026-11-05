// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_reset_from_async_domain.sv: the registers that generate rb are clocked by clkb, the domain of the registers they reset (relation Same).
// expect-none: RDC-007
module top(input logic clka, clkb, arst_n, d, output logic q);
  logic ra, rb;
  always_ff @(posedge clkb or negedge arst_n) if (!arst_n) {rb, ra} <= 2'b00; else {rb, ra} <= {ra, 1'b1};
  always_ff @(posedge clkb or negedge rb) if (!rb) q <= 1'b0; else q <= d;
endmodule
