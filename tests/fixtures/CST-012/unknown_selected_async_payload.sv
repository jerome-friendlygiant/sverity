// sverity-args: --top top --sdc @DIR@/selected_async_payload.sdc
// expect: CST-012 ERROR
// The computed select and mixed cone do not establish an exact qualified bit path.
// expect-coverage: constraints INCOMPLETE
module top(input logic clk_a, clk_b, rst_ni, output logic q);
  logic [1:0] payload_q;
  logic index_q;
  always_ff @(posedge clk_a or negedge rst_ni)
    if (!rst_ni) payload_q <= '0;
    else payload_q <= payload_q + 1'b1;
  always_ff @(posedge clk_b or negedge rst_ni)
    if (!rst_ni) index_q <= 1'b0;
    else index_q <= ~index_q;
  assign q = payload_q[index_q];
endmodule
