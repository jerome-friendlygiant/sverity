// sverity-args: --top top --sdc @DIR@/bus_inputs.sdc
// MUTATION of CDC-039/obligation_qualified_payload.sv: the bus bits are synchronised one by one and the destination
// captures the synchronised bus directly, with no qualifying control.
// expect: CDC-009 WARNING
// revert: else {s2, s1} <= {s1, data}; => else {s2, s1} <= '0;
// revert: else q <= s2; => else if (v2) q <= data;
// revert-expect-none: CDC-009 WARNING
module top(input logic clka, clkb, rst_n, load, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] data, s1, s2;
  logic v_a, v1, v2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin data <= '0; v_a <= 1'b0; end else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, data};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= '0; else q <= s2;
endmodule
