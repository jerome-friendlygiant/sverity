// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two asynchronous controls tested by a casez on their values: their forced values are not analysed, so RDC-026 is Unresolved and reset-extraction is INCOMPLETE.
// expect-unresolved: RDC-026
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, a, b, d, output logic q);
  always_ff @(posedge clka or negedge a or negedge b)
    casez ({a, b})
      2'b0?: q <= 0;
      2'b10: q <= 1;
      default: q <= d;
    endcase
endmodule
