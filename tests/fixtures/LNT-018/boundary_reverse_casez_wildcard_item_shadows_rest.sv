// sverity-args: --top top
// A casez item with a '?' wildcard matches 1'b1 when reached, so the later runtime item and the default can never run.
// expect: LNT-018 WARNING 7
module top(input logic is_a, output logic [1:0] sel);
  always_comb begin
    sel = 2'd0;
    casez (1'b1)
      1'b?:    sel = 2'd1;
      is_a:    sel = 2'd2;
      default: sel = 2'd0;
    endcase
  end
endmodule
