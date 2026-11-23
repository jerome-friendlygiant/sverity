// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-015 INFO
// expect: CDC-024 WARNING
// expect-count: CDC-013 WARNING 1
// expect-count: CDC-014 WARNING 1
// expect: CDC-013 WARNING 13
// expect: CDC-014 WARNING 13
// 2-phase handshake: req toggles, ack mirrors via the zz copy; the a_ copy is used as a level event (no edge detector)
module top(input logic clka, clkb, rst_n, start, output logic busy, output logic [7:0] cnt);
  logic req, zz1, zz2, a_1, a_2, ack, a1, a2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 1'b0; else if (start && (req == a2)) req <= ~req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {zz2, zz1} <= '0; else {zz2, zz1} <= {zz1, req};
  always_ff @(posedge clkb) {a_2, a_1} <= {a_1, req};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 1'b0; else ack <= zz2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) cnt <= '0; else if (a_2) cnt <= cnt + 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2, a1} <= '0; else {a2, a1} <= {a1, ack};
  assign busy = req != a2;
endmodule
