// sverity-args: --goal rdc --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-015.reach_budget=1
// The grouped reset obligation survives when the endpoint search cannot finish.
// expect: RDC-023 WARNING
// expect-unresolved: RDC-023
// expect-coverage: cdc-015-handshake-reachability INCOMPLETE
module top(input logic clka, clkb, rst_n, req_input, ack_input, output logic observed);
  logic req_helper, req, req_meta0, req_meta;
  logic ack_helper, ack, ack_meta0, ack_meta;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin req_helper <= 1'b0; req <= 1'b0; end
    else begin req_helper <= req_input; req <= req_helper; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin req_meta0 <= 1'b0; req_meta <= 1'b0; end
    else begin req_meta0 <= req; req_meta <= req_meta0; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin ack_helper <= 1'b0; ack <= 1'b0; end
    else begin ack_helper <= ack_input; ack <= ack_helper; end
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin ack_meta0 <= 1'b0; ack_meta <= 1'b0; end
    else begin ack_meta0 <= ack; ack_meta <= ack_meta0; end
  assign observed = ack_meta;
endmodule
