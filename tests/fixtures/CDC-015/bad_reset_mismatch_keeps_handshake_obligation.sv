// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-024 WARNING
// expect: CDC-026 ERROR
module top(input logic clka, clkb, rst_n, rst2_n, output logic done);
  logic req, r1, r2, ack, a1, a2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 0; else if (a2 == req) req <= ~req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r1 <= 0; else r1 <= req;
  always_ff @(posedge clkb or negedge rst2_n) if (!rst2_n) r2 <= 0; else r2 <= r1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 0; else ack <= r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a1 <= 0; else a1 <= ack;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a2 <= 0; else a2 <= a1;
  assign done = a2;
endmodule
