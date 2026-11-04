// sverity-args: --top top --sdc @FIXTURES@/_common/three_async.sdc
// expect: CDC-003 ERROR
// expect: CDC-028 ERROR
// expect-none: CDC-039
// revert: posedge clkc => posedge clkb
// revert-expect-none: CDC-003 ERROR
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, clkc, rst_n, value, event_in, output logic result);
  logic payload, control, mid, qualified_elsewhere;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin payload <= 0; control <= 0; end
    else begin payload <= value; control <= event_in; end
  always_ff @(posedge clkc or negedge rst_n)
    if (!rst_n) begin mid <= 0; qualified_elsewhere <= 0; end
    else begin mid <= control; qualified_elsewhere <= mid; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) result <= 0;
    else if (qualified_elsewhere) result <= payload;
endmodule
