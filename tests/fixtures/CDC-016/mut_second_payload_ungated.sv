// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of CDC-039/obligation_qualified_payload.sv: a second payload is captured without the synchronised valid.
// expect: CDC-016 ERROR
// revert: r <= data2; => if (v2) r <= data2;
// revert-expect-none: CDC-016 ERROR
module top(input logic clka, clkb, rst_n, load, input logic [3:0] d, e, output logic [3:0] q, r);
  logic [3:0] data, data2;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin data <= '0; data2 <= '0; v_a <= 1'b0; end else begin data <= d; data2 <= e; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin q <= '0; r <= '0; end else begin if (v2) q <= data; r <= data2; end
endmodule
