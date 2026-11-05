// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of mut_ungated_payload: one unsynchronised bit beside a synchronised control is a CDC-003, not a bus payload.
// expect: CDC-003 ERROR
// expect-none: CDC-016
module top(input logic clka, clkb, rst_n, load, d, output logic q);
  logic data, v_a, v1, v2, vq;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin q <= '0; vq <= 1'b0; end else begin q <= data; vq <= v2; end
endmodule
