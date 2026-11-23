// sverity-args: --top top --goal cdc --sdc @FIXTURES@/_common/async.sdc --profile fpga
// The delayed copy may implement event history. The selected output does not
// establish that every modeled use of the copy belongs to an edge detector.
// expect: CDC-012 WARNING
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, d, select_history, output logic observed);
  logic launch;
  (* ASYNC_REG = "TRUE" *) logic s1, s2;
  logic history;

  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) begin
    s1 <= launch;
    s2 <= s1;
    history <= s2;
  end

  assign observed = select_history ? history : (s2 ^ history);
endmodule
