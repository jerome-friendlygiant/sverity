// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two request capture endpoints and two acknowledgements share both controls.
// The dependency graph cannot establish which acknowledge owns the request.
// expect-count: CDC-015 WARNING 2
// expect-unresolved: CDC-015
// expect-count: CDC-024 WARNING 2
// expect-unresolved: CDC-024
module top(input logic clka, clkb, rst_n, start, output logic req_out);
  logic req, req_meta0, req_seen0, req_meta1, req_seen1;
  logic ack0, ack0_meta, ack0_seen, ack1, ack1_meta, ack1_seen;

  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) req <= 1'b0;
    else if (start && ack0_seen && ack1_seen) req <= ~req;

  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin req_meta0 <= 1'b0; req_seen0 <= 1'b0; req_meta1 <= 1'b0; req_seen1 <= 1'b0; end
    else begin req_meta0 <= req; req_seen0 <= req_meta0; req_meta1 <= req; req_seen1 <= req_meta1; end

  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin ack0 <= 1'b0; ack1 <= 1'b0; end
    else begin ack0 <= req_seen0 | req_seen1; ack1 <= req_seen0 | req_seen1; end

  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin ack0_meta <= 1'b0; ack0_seen <= 1'b0; ack1_meta <= 1'b0; ack1_seen <= 1'b0; end
    else begin ack0_meta <= ack0; ack0_seen <= ack0_meta; ack1_meta <= ack1; ack1_seen <= ack1_meta; end

  assign req_out = req;
endmodule
