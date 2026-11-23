// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_hierarchical_handshake.sv: the acknowledge owner ignores req_seen.
// expect: CDC-015 WARNING
// revert: else ack <= ~ack; => else ack <= req_seen;
// revert-expect-none: CDC-015 WARNING
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
    else ack <= ~ack;
endmodule

module ack_capture(input logic clk, rst_n, ack, output logic ack_seen);
  logic ack_meta;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin ack_meta <= 1'b0; ack_seen <= 1'b0; end
    else begin ack_meta <= ack; ack_seen <= ack_meta; end
endmodule

module top(input logic clka, clkb, rst_n, start, output logic busy);
  logic req, req_seen, ack, ack_seen;
  req_owner u_req_owner(.clk(clka), .rst_n, .start, .ack_seen, .req);
  req_capture u_req_capture(.clk(clkb), .rst_n, .req, .req_seen);
  ack_owner u_ack_owner(.clk(clkb), .rst_n, .req_seen, .ack);
  ack_capture u_ack_capture(.clk(clka), .rst_n, .ack, .ack_seen);
  assign busy = req;
endmodule
