// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The asynchronous reset is tested by a case statement, so the value it forces is not read as a constant: the reset is still an asynchronous source (RDC-012 and RDC-013 report it), and the reset extraction is INCOMPLETE rather than COMPLETE.
// expect: RDC-013 WARNING
// expect: RDC-012 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) begin
    case (rst_n)
      1'b0: q <= 1'b0;
      default: q <= d;
    endcase
  end
endmodule
