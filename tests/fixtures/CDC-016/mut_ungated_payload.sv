// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of CDC-039/good_qualified_payload.sv: the capture ignores the synchronised valid.
// expect: CDC-016 ERROR
// CDC-004 is evidence of the primary CDC-016 finding
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data;
  logic v_a, v1, v2, vq;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin q <= '0; vq <= 1'b0; end else begin q <= data; vq <= v2; end
endmodule
