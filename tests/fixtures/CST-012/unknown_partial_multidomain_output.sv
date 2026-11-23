// sverity-args: --top top --sdc @DIR@/partial_multidomain.sdc
// expect: CST-012 WARNING 9
// expect-unresolved: CST-012 9
// expect-coverage: constraints INCOMPLETE
module top (input logic a, b, output logic [1:0] q);
    logic x, y;
    always_ff @(posedge a) x <= ~x;
    always_ff @(posedge b) y <= ~y;
    assign q = {x, y};
endmodule
