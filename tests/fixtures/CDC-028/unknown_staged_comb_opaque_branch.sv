// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Staged always_comb with an opaque-function qualifier stays Unresolved rather than false-safe or false-error.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-coverage: payload-qualification INCOMPLETE
// expect-none: CDC-028
module top(input logic clka, clkb, rst_n, load, ready, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data, data_d;
  logic v_a, v1, v2;
  function automatic logic gate_fn(logic a, b);
    return a ^ b;
  endfunction
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; v_a <= 1'b0; end
    else begin data <= d; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    data_d = q;
    if (gate_fn(v2, ready)) data_d = data;
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
