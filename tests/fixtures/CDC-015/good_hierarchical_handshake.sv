// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Request and acknowledge owners and both synchronizers elaborate under different instance paths.
// A reciprocal endpoint pair is one CDC-015 structure; the protocol stays an open CDC-024 obligation.
// expect-count: CDC-015 INFO 2
// expect-count: CDC-024 WARNING 2
// expect-count: RDC-023 WARNING 2
// expect-none: CDC-013 WARNING
// expect-none: CDC-014 WARNING
module req_owner(input logic clk, rst_n, start, ack_seen, output logic req);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) req <= 1'b0;
    else if (start && !req && !ack_seen) req <= 1'b1;
    else if (ack_seen) req <= 1'b0;
endmodule

module req_capture(input logic clk, rst_n, req, output logic req_seen);
  logic req_meta;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin req_meta <= 1'b0; req_seen <= 1'b0; end
    else begin req_meta <= req; req_seen <= req_meta; end
endmodule

module ack_owner(input logic clk, rst_n, req_seen, output logic ack);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) ack <= 1'b0;
    else ack <= req_seen;
endmodule

module ack_capture(input logic clk, rst_n, ack, output logic ack_seen);
  logic ack_meta;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin ack_meta <= 1'b0; ack_seen <= 1'b0; end
    else begin ack_meta <= ack; ack_seen <= ack_meta; end
endmodule

module channel(input logic clka, clkb, rst_n, start, output logic busy);
  logic req, req_seen, ack, ack_seen;
  req_owner req_owner_i(.clk(clka), .rst_n, .start, .ack_seen, .req);
  req_capture req_capture_i(.clk(clkb), .rst_n, .req, .req_seen);
  ack_owner ack_owner_i(.clk(clkb), .rst_n, .req_seen, .ack);
  ack_capture ack_capture_i(.clk(clka), .rst_n, .ack, .ack_seen);
  assign busy = req;
endmodule

// Two independent channels share clocks but have no endpoint dependency between them.
module top(input logic clka, clkb, rst_n, start0, start1, output logic busy0, busy1);
  channel channel0(.clka, .clkb, .rst_n, .start(start0), .busy(busy0));
  channel channel1(.clka, .clkb, .rst_n, .start(start1), .busy(busy1));
endmodule
