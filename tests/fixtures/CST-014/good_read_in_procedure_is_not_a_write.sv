// sverity-args: --top top --sdc @DIR@/good_read_in_procedure_is_not_a_write.sdc
// A displayed value and a select index are reads of mode, so its value applies and reaches x.
// expect: CST-014 WARNING
// expect-none: CST-009
// expect-none: CDC-028
// expect-coverage: case-analysis-modes INCOMPLETE
module top(input logic a, b, mode, output logic q, output logic [1:0] v);
  wire x = mode;
  logic source_q;
  always_ff @(posedge a) begin
    source_q <= ~source_q;
    v[mode] <= source_q;
    $display("mode=%b", mode);
  end
  always_ff @(posedge b) if (x) q <= source_q;
endmodule
