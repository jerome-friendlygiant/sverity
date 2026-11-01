// sverity-args: --top top
// expect: STY-010 WARNING
module top #(parameter bit P = 1) (input logic a, output logic y);
  if (P) begin
    assign y = a;
  end else begin
    assign y = ~a;
  end
endmodule
