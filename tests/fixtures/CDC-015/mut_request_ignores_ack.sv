// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_handshake.sv: the request no longer depends on the acknowledge.
// expect: CDC-015 WARNING
module top(input logic clka, clkb, rst_n, start, output logic busy);
  logic req, r1, r2, ack, a1, a2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 1'b0; else req <= start;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {r2, r1} <= '0; else {r2, r1} <= {r1, req};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 1'b0; else ack <= r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, ack};
  assign busy = req | a2;
endmodule
