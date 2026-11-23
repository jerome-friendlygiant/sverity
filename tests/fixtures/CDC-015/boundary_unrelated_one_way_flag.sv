// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A one-way flag that consumes the acknowledgement does not displace the unique reciprocal request/acknowledge pair.
// expect-count: CDC-015 INFO 1
// expect-count: CDC-015 WARNING 1
module top(input logic clka, clkb, rst_n, start, output logic busy);
  logic req, r1, r2, ack, a1, a2, flag, f1, f2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) req <= 0; else if (start && !req) req <= 1; else if (a2) req <= 0;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {r2,r1} <= 0; else {r2,r1} <= {r1,req};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) ack <= 0; else ack <= r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {a2,a1} <= 0; else {a2,a1} <= {a1,ack};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) flag <= 0; else flag <= a2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {f2,f1} <= 0; else {f2,f1} <= {f1,flag};
  assign busy = req | f2;
endmodule
