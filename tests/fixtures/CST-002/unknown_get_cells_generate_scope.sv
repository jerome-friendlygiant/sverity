// sverity-args: --top top --sdc @DIR@/generate_scope_cell.sdc
// expect: CST-002 WARNING
// expect-unresolved: CST-002
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, d, output logic q);
  if (1) begin : g
    wire internal_net = ~d;
    assign q = internal_net;
  end
endmodule
